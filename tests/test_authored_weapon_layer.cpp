#include "authored_weapon_layer.h"
#include "authored_weapon_pose.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
using Layer = AuthoredSpritePart;
using Json = nlohmann::json;

const AuthoredSpriteDraw &findDraw(const AuthoredWeaponComposition &c, Layer part)
{
    for (std::size_t i = 0; i < c.count; ++i)
        if (c.draws[i].part == part) return c.draws[i];
    throw std::logic_error("missing rendered layer");
}

void sameRect(SDL_FRect a, SDL_FRect b)
{
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.w, b.w);
    EXPECT_FLOAT_EQ(a.h, b.h);
}

SDL_FRect sourceRect(const Json &value)
{
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

class AuthoredWeaponLayerTest : public testing::Test
{
protected:
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    AuthoredWeaponLayer layer;
    Json atlas;

    void SetUp() override
    {
        surface = SDL_CreateSurface(160, 160, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr) << SDL_GetError();
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
        ASSERT_TRUE(layer.load(renderer, root)) << SDL_GetError();
        std::ifstream in(root / "characters/player/authored_weapon_v1/atlas.json");
        in >> atlas;
    }

    void TearDown() override
    {
        layer = {};
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
    }

    std::vector<Uint8> render(const AuthoredWeaponComposition &c)
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        EXPECT_TRUE(SDL_RenderClear(renderer));
        layer.draw(renderer, {16, 16, 128, 128}, c);
        EXPECT_TRUE(SDL_RenderPresent(renderer));
        const auto *pixels = static_cast<const Uint8 *>(surface->pixels);
        return {pixels, pixels + surface->pitch * surface->h};
    }
};

TEST_F(AuthoredWeaponLayerTest, AllUnarmedSupportFootEntriesUseTheirOwnAuthoredPixels)
{
    for (const auto &entry : atlas.at("unarmedEntryVariants"))
    {
        const bool left = entry.at("facing") == "left";
        const auto source = entry.at("walkFrame").get<unsigned>();
        AuthoredWeaponPoseInput input;
        input.motion = {source >= 4 ? PlayerMotionClip::WalkToRunFar : PlayerMotionClip::WalkToRunNear,
                        0, left, static_cast<std::uint8_t>(source % 4)};
        for (const auto &frame : entry.at("frames"))
        {
            AuthoredWeaponPoseClock clock;
            clock.update(input, 0);
            const auto c = layer.compose(clock.frame());
            ASSERT_EQ(c.count, 1U);
            EXPECT_EQ(c.draws.front().part, Layer::Body);
            sameRect(c.draws.front().source, sourceRect(frame.at("body")));
            ++input.motion.index;
        }
    }
}

TEST_F(AuthoredWeaponLayerTest, PickupOrderFreeHandsAndMovingFeetFollowAuthoredFrames)
{
    for (const auto &clip : atlas.at("clips"))
    {
        const bool large = clip.at("key") == "large_draw_hip";
        if (!large && clip.at("key") != "small_draw_hip") continue;
        const bool left = clip.at("facing") == "left";
        AuthoredWeaponPoseFrame pose;
        pose.gun = large ? AuthoredGunKind::Rifle : AuthoredGunKind::Pistol;
        pose.motion = {PlayerMotionClip::Walk, 1, left};
        pose.bodyClip = left ? "modv2-held-walk-left" : "modv2-held-walk-right";
        pose.bodyIndex = 1;
        pose.aimDirection = {left ? -1.0F : 1.0F, 0};
        const auto steady = layer.compose(pose);
        pose.drawing = true;
        for (const auto &f : clip.at("frames"))
        {
            SCOPED_TRACE(pose.upperIndex);
            const auto &authored = f.at("loadouts").at(large ? "large" : "small");
            const auto c = layer.compose(pose);
            ASSERT_EQ(c.count, 6U);
            sameRect(findDraw(c, Layer::NearFoot).source, findDraw(steady, Layer::NearFoot).source);
            sameRect(findDraw(c, Layer::FarFoot).source, findDraw(steady, Layer::FarFoot).source);
            sameRect(findDraw(c, Layer::UpperBody).source, findDraw(steady, Layer::UpperBody).source);
            const auto &gun = findDraw(c, Layer::Gun);
            const auto &hand = findDraw(c, Layer::NearHand);
            const auto &far = findDraw(c, Layer::FarHand);
            if (authored.at("hands").at("near").at("attached") == true)
            {
                EXPECT_FLOAT_EQ(hand.position.x, gun.position.x);
                EXPECT_FLOAT_EQ(hand.position.y, gun.position.y);
            }
            else EXPECT_GT(std::hypot(hand.position.x - gun.position.x, hand.position.y - gun.position.y), 1.0);
            if (authored.at("hands").at("far").at("attached") == false)
            {
                const auto &at = authored.at("hands").at("far").at("at");
                EXPECT_FLOAT_EQ(far.position.x - gun.position.x, at[0].get<float>() - authored.at("weapon").at("at")[0].get<float>());
                EXPECT_FLOAT_EQ(far.position.y - gun.position.y, at[1].get<float>() - authored.at("weapon").at("at")[1].get<float>());
            }
            const auto &order = authored.at("order");
            const auto gunIndex = std::find(order.begin(), order.end(), "weapon") - order.begin();
            EXPECT_EQ(c.draws[gunIndex].part, Layer::Gun);
            EXPECT_NE(std::find(order.begin(), order.end(), "upper"), order.end());
            const auto torsoIndex = std::find(order.begin(), order.end(), "upper") - order.begin();
            EXPECT_EQ(c.draws[torsoIndex].part, Layer::UpperBody);
            ++pose.upperIndex;
        }
    }
}

TEST_F(AuthoredWeaponLayerTest, AllGunProfilesKeepBothGripsOnADiagonalAndReverseAimContinuously)
{
    for (bool left : {false, true})
        for (auto gun : {AuthoredGunKind::Rifle, AuthoredGunKind::Compact, AuthoredGunKind::Pistol})
        {
            AuthoredWeaponPoseInput input;
            input.motion.left = left;
            input.active = {17, gun};
            input.aimDirection = {left ? -0.6F : 0.6F, -0.8F};
            input.aimRequested = true;
            input.adsProgress = 0.47F;
            AuthoredWeaponPoseClock clock;
            clock.update(input, 0.0F);
            const auto rising = layer.compose(clock.frame());
            ASSERT_EQ(rising.count, 6U);
            auto pose = clock.frame();
            const auto &g = findDraw(rising, Layer::Gun);
            const auto &n = findDraw(rising, Layer::NearHand);
            const auto &f = findDraw(rising, Layer::FarHand);
            EXPECT_FLOAT_EQ(n.position.x, g.position.x);
            EXPECT_FLOAT_EQ(n.position.y, g.position.y);
            const auto key = std::string(left ? "left.weapon." : "right.weapon.") +
                             (gun == AuthoredGunKind::Pistol ? "small" : gun == AuthoredGunKind::Compact ? "compact" : "large");
            const auto &definition = atlas.at("parts").at(key);
            const auto &a = definition.at("nearGrip"), &b = definition.at("farGrip");
            const auto x = b[0].get<double>() - a[0].get<double>(), y = b[1].get<double>() - a[1].get<double>();
            const auto angle = g.angle * std::numbers::pi / 180.0;
            EXPECT_NEAR(f.position.x, g.position.x + x * std::cos(angle) - y * std::sin(angle), 0.01);
            EXPECT_NEAR(f.position.y, g.position.y + x * std::sin(angle) + y * std::cos(angle), 0.01);

            input.aimRequested = false;
            clock.update(input, 0.0F);
            const auto lowering = layer.compose(clock.frame());
            const auto &other = findDraw(lowering, Layer::Gun);
            EXPECT_NEAR(g.position.x, other.position.x, 0.01);
            EXPECT_NEAR(g.position.y, other.position.y, 0.01);
            const auto pixels = render(rising);
            EXPECT_TRUE(std::any_of(pixels.begin(), pixels.end(), [](auto v) { return v != 0; }));
            EXPECT_EQ(render(layer.compose(pose)), pixels);
            pose.aimDirection.y *= -1;
            EXPECT_NE(render(layer.compose(pose)), pixels);
        }
}
}

