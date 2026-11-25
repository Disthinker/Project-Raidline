#include "authored_weapon_layer.h"
#include "authored_weapon_pose.h"
#include "texture.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
using Json = nlohmann::json;
using Layer = AuthoredSpritePart;

struct Part
{
    SDL_FRect rect{};
    SDL_FPoint pivot{}, farGrip{};
};

struct Placement
{
    const Part *part{};
    SDL_FPoint at{};
    float angle{};
    bool attached{};
};

struct Overlay
{
    Placement gun, nearHand, farHand;
    std::array<Layer, 6> order{};
};

struct Frame
{
    SDL_FRect body{}, upper{}, footFar{}, footNear{};
    SDL_FPoint torso{};
    std::array<Overlay, 3> profiles{};
    bool armed{};
};

using Clip = std::vector<Frame>;
using PartMap = std::map<std::string, Part, std::less<>>;

template<class T>
const T &frameAt(const std::vector<T> &clip, std::size_t index)
{
    return clip[std::min(index, clip.size() - 1)];
}

SDL_FPoint add(SDL_FPoint a, SDL_FPoint b) { return {a.x + b.x, a.y + b.y}; }
SDL_FPoint subtract(SDL_FPoint a, SDL_FPoint b) { return {a.x - b.x, a.y - b.y}; }
SDL_FPoint blend(SDL_FPoint a, SDL_FPoint b, float t) { return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t)}; }
SDL_FPoint rotate(SDL_FPoint point, float degrees)
{
    const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
    const float c = std::cos(radians), s = std::sin(radians);
    return {point.x * c - point.y * s, point.x * s + point.y * c};
}

SDL_FPoint readPoint(const Json &j) { return {j.at(0).get<float>(), j.at(1).get<float>()}; }
SDL_FRect readRect(const Json &j, SDL_FRect bounds)
{
    SDL_FRect rect{j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()};
    if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 ||
        rect.x + rect.w > bounds.w || rect.y + rect.h > bounds.h)
        throw std::runtime_error("sprite rectangle outside texture");
    return rect;
}

constexpr std::array<std::pair<std::string_view, Layer>, 6> layerNames{{
    {"footFar", Layer::FarFoot}, {"upper", Layer::UpperBody}, {"footNear", Layer::NearFoot},
    {"far", Layer::FarHand}, {"weapon", Layer::Gun}, {"near", Layer::NearHand}
}};

Overlay readOverlay(const Json &j, const PartMap &parts)
{
    const auto placement = [&](const Json &sprite) {
        return Placement{&parts.at(sprite.at("part").get<std::string>()), readPoint(sprite.at("at")),
                         sprite.at("angle").get<float>(), sprite.value("attached", false)};
    };
    Overlay result;
    result.gun = placement(j.at("weapon"));
    result.nearHand = placement(j.at("hands").at("near"));
    result.farHand = placement(j.at("hands").at("far"));
    const auto &order = j.at("order");
    if (order.size() != result.order.size()) throw std::runtime_error("invalid layer count");
    for (std::size_t i = 0; i < result.order.size(); ++i)
    {
        const auto &name = order.at(i).get_ref<const std::string &>();
        const auto it = std::find_if(layerNames.begin(), layerNames.end(),
                                     [&](const auto &entry) { return entry.first == name; });
        if (it == layerNames.end() || std::find(result.order.begin(), result.order.begin() + i, it->second) != result.order.begin() + i)
            throw std::runtime_error("unknown or repeated sprite layer");
        result.order[i] = it->second;
    }
    return result;
}

Clip readClip(const Json &j, const PartMap &parts, SDL_FRect bounds)
{
    Clip clip;
    for (const auto &f : j.at("frames"))
    {
        Frame frame;
        frame.body = readRect(f.at("body"), bounds);
        frame.upper = readRect(f.at("upper"), bounds);
        frame.footFar = readRect(f.at("footFar"), bounds);
        frame.footNear = readRect(f.at("footNear"), bounds);
        frame.torso = readPoint(f.at("torsoPivot"));
        const auto &profiles = f.at("loadouts");
        frame.armed = !profiles.empty();
        if (frame.armed)
        {
            frame.profiles[0] = readOverlay(profiles.at("large"), parts);
            frame.profiles[1] = readOverlay(profiles.at("compact"), parts);
            frame.profiles[2] = readOverlay(profiles.at("small"), parts);
        }
        clip.push_back(frame);
    }
    if (clip.empty()) throw std::runtime_error("empty authored clip");
    return clip;
}

Texture loadSheet(SDL_Renderer *renderer, const std::filesystem::path &path)
{
    const auto utf8 = path.u8string();
    Texture image{IMG_LoadTexture(renderer, reinterpret_cast<const char *>(utf8.c_str()))};
    if (!image.valid() || !SDL_SetTextureScaleMode(image.get(), SDL_SCALEMODE_NEAREST) ||
        !SDL_SetTextureBlendMode(image.get(), SDL_BLENDMODE_BLEND))
        throw std::runtime_error(SDL_GetError());
    return image;
}

// Align overlay samples on the moving body's torso before blending.
Overlay aligned(const Frame &frame, std::size_t profile, SDL_FPoint torso)
{
    auto overlay = frame.profiles[profile];
    const auto offset = subtract(torso, frame.torso);
    for (auto *piece : {&overlay.gun, &overlay.nearHand, &overlay.farHand})
        piece->at = add(piece->at, offset);
    return overlay;
}

Placement blendPiece(const Placement &a, const Placement &b, float t)
{
    Placement p = t < 0.5F ? a : b;
    p.at = blend(a.at, b.at, t);
    p.angle = a.angle + std::remainder(b.angle - a.angle, 360.0F) * t;
    return p;
}

Overlay blendOverlay(const Overlay &a, const Overlay &b, float t)
{
    return {blendPiece(a.gun, b.gun, t), blendPiece(a.nearHand, b.nearHand, t),
            blendPiece(a.farHand, b.farHand, t), t < 0.5F ? a.order : b.order};
}

AuthoredSpriteDraw sprite(Layer kind, const Placement &p)
{
    return {kind, p.part->rect, p.at, p.part->pivot, p.angle};
}
}

struct AuthoredWeaponLayer::Data
{
    Texture body, parts;
    PartMap definitions;
    std::map<std::string, Clip, std::less<>> clips;
    std::array<std::map<std::string, const Clip *, std::less<>>, 2> byKey;
    std::array<std::array<Clip, 8>, 2> unarmedStart;
};

AuthoredWeaponLayer::AuthoredWeaponLayer() = default;
AuthoredWeaponLayer::~AuthoredWeaponLayer() = default;
AuthoredWeaponLayer::AuthoredWeaponLayer(AuthoredWeaponLayer &&) noexcept = default;
AuthoredWeaponLayer &AuthoredWeaponLayer::operator=(AuthoredWeaponLayer &&) noexcept = default;

bool AuthoredWeaponLayer::load(SDL_Renderer *renderer, const std::filesystem::path &assetRoot)
{
    try
    {
        const auto directory = assetRoot / "characters/player/authored_weapon_v1";
        auto data = std::make_unique<Data>();
        data->body = loadSheet(renderer, directory / "body.png");
        data->parts = loadSheet(renderer, directory / "parts.png");
        const auto bodyBounds = data->body.contentBounds(), partBounds = data->parts.contentBounds();
        std::ifstream input(directory / "atlas.json", std::ios::binary);
        const auto atlas = Json::parse(input);
        if (atlas.at("schema") != "raidline-authored-weapon-v1" || atlas.at("anchor") != Json::array({32, 60}))
            throw std::runtime_error("unsupported authored player atlas");
        for (const auto &[name, part] : atlas.at("parts").items())
        {
            const auto pivot = readPoint(part.at("pivot"));
            data->definitions.emplace(name, Part{readRect(part.at("rect"), partBounds), pivot,
                                                  part.contains("farGrip") ? readPoint(part.at("farGrip")) : pivot});
        }
        for (const auto &j : atlas.at("clips"))
        {
            const bool left = j.at("facing") == "left";
            const auto [entry, added] = data->clips.emplace(j.at("id").get<std::string>(), readClip(j, data->definitions, bodyBounds));
            if (!added || !data->byKey[left].emplace(j.at("key").get<std::string>(), &entry->second).second)
                throw std::runtime_error("duplicate authored clip");
        }
        for (const auto &j : atlas.at("unarmedEntryVariants"))
        {
            auto &variant = data->unarmedStart[j.at("facing") == "left"].at(j.at("walkFrame").get<std::size_t>());
            if (!variant.empty()) throw std::runtime_error("duplicate walk-to-run entry");
            variant = readClip(j, data->definitions, bodyBounds);
        }
        for (const auto &side : data->unarmedStart)
            for (const auto &entry : side)
                if (entry.empty()) throw std::runtime_error("missing walk-to-run entry");
        if (data->clips.size() != 68) throw std::runtime_error("incomplete authored clip set");
        data_ = std::move(data);
        return true;
    }
    catch (const std::exception &e)
    {
        SDL_SetError("Authored player atlas: %s", e.what());
        return false;
    }
}

AuthoredWeaponComposition AuthoredWeaponLayer::compose(const AuthoredWeaponPoseFrame &pose) const
{
    AuthoredWeaponComposition result;
    if (!data_) return result;
    const auto bodyClip = data_->clips.find(pose.bodyClip);
    if (bodyClip == data_->clips.end()) return result;
    const bool left = pose.motion.left;
    const Clip *clip = &bodyClip->second;
    if (pose.gun == AuthoredGunKind::Unarmed &&
        (pose.motion.clip == PlayerMotionClip::WalkToRunNear || pose.motion.clip == PlayerMotionClip::WalkToRunFar))
    {
        const auto entry = (pose.motion.clip == PlayerMotionClip::WalkToRunFar ? 4 : 0) + pose.motion.entryPhase % 4;
        clip = &data_->unarmedStart[left][entry];
    }
    const Frame &body = frameAt(*clip, pose.bodyIndex);
    if (pose.gun == AuthoredGunKind::Unarmed)
    {
        result.draws[result.count++] = {Layer::Body, body.body};
        return result;
    }
    if (!body.armed) return result;
    const auto profile = static_cast<std::size_t>(pose.gun) - 1;
    auto overlay = body.profiles.at(profile);
    const float height = std::clamp(pose.aimHeight, 0.0F, 1.0F);
    const bool small = pose.gun == AuthoredGunKind::Pistol;
    const auto &byKey = data_->byKey[left];
    if (pose.drawing)
    {
        const auto &hip = frameAt(*byKey.at(small ? "small_draw_hip" : "large_draw_hip"), pose.upperIndex);
        const auto &aim = frameAt(*byKey.at(small ? "small_draw_aim" : "large_draw_aim"), pose.upperIndex);
        overlay = blendOverlay(aligned(hip, profile, body.torso), aligned(aim, profile, body.torso), height);
    }
    else if (height > 0.0F)
    {
        const auto upperClip = data_->clips.find(pose.upperClip);
        if (upperClip == data_->clips.end()) return result;
        const auto &track = upperClip->second;
        const float progress = pose.aimingUp ? height : 1.0F - height;
        const float index = progress * static_cast<float>(track.size() - 1);
        const auto i = static_cast<std::size_t>(index);
        const auto &a = track[i], &b = frameAt(track, i + 1);
        const float t = index - static_cast<float>(i);
        auto aimed = blendOverlay(aligned(a, profile, body.torso), aligned(b, profile, body.torso), t);
        // Carry the gait's authored grip offset into the lift and let it taper at full ADS.
        const auto hip = aligned(pose.aimingUp ? track.front() : track.back(), profile, body.torso);
        const auto hipOffset = subtract(overlay.gun.at, hip.gun.at);
        const auto offset = blend(hipOffset, {}, height);
        for (auto *piece : {&aimed.gun, &aimed.nearHand, &aimed.farHand})
            piece->at = add(piece->at, offset);
        overlay = aimed;
    }

    float aimAngle = 0.0F;
    if (pose.aimDirection.x != 0.0F || pose.aimDirection.y != 0.0F)
        aimAngle = std::remainder(std::atan2(pose.aimDirection.y, pose.aimDirection.x) * 180.0F /
                                  std::numbers::pi_v<float> - (left ? 180.0F : 0.0F), 360.0F);
    // A stowed gun keeps its authored angle; the near hand steers it after pickup.
    if (pose.drawing)
    {
        const float lift = small ? 35.0F / 210.0F : 75.0F / 315.0F;
        aimAngle *= std::clamp((pose.drawProgress - lift) / (1.0F - lift), 0.0F, 1.0F);
    }
    overlay.gun.angle += aimAngle;
    for (auto *hand : {&overlay.farHand, &overlay.nearHand})
    {
        if (!hand->attached) continue;
        const auto grip = hand == &overlay.nearHand ? SDL_FPoint{} : subtract(overlay.gun.part->farGrip, overlay.gun.part->pivot);
        hand->at = add(overlay.gun.at, rotate(grip, overlay.gun.angle));
        hand->angle += aimAngle;
    }

    for (const auto layer : overlay.order)
    {
        auto &draw = result.draws[result.count++];
        switch (layer)
        {
        case Layer::FarFoot:   draw = {layer, body.footFar}; break;
        case Layer::NearFoot:  draw = {layer, body.footNear}; break;
        // The draw/lift tracks animate the separate hands and gun. Their neutral
        // torsos would erase the moving body's authored lean and breathing.
        case Layer::UpperBody: draw = {layer, body.upper}; break;
        case Layer::Gun:       draw = sprite(layer, overlay.gun); break;
        case Layer::NearHand:  draw = sprite(layer, overlay.nearHand); break;
        case Layer::FarHand:   draw = sprite(layer, overlay.farHand); break;
        default: break;
        }
    }
    return result;
}

void AuthoredWeaponLayer::draw(SDL_Renderer *renderer, const SDL_FRect &bounds,
                               const AuthoredWeaponComposition &composition) const
{
    if (!data_) return;
    const float sx = bounds.w / 64.0F, sy = bounds.h / 64.0F;
    for (std::size_t i = 0; i < composition.count; ++i)
    {
        const auto &d = composition.draws[i];
        const bool fromParts = d.part == Layer::Gun || d.part == Layer::NearHand || d.part == Layer::FarHand;
        auto *sheet = (fromParts ? data_->parts : data_->body).get();
        const SDL_FRect dst{bounds.x + (d.position.x - d.pivot.x) * sx, bounds.y + (d.position.y - d.pivot.y) * sy,
                            d.source.w * sx, d.source.h * sy};
        if (d.angle == 0.0) SDL_RenderTexture(renderer, sheet, &d.source, &dst);
        else
        {
            const SDL_FPoint center{d.pivot.x * sx, d.pivot.y * sy};
            SDL_RenderTextureRotated(renderer, sheet, &d.source, &dst, d.angle, &center, SDL_FLIP_NONE);
        }
    }
}

