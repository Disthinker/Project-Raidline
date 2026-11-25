#pragma once

#include <SDL3/SDL_rect.h>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>

struct SDL_Renderer;
struct AuthoredWeaponPoseFrame;

enum class AuthoredSpritePart { Body, FarFoot, UpperBody, NearFoot, FarHand, Gun, NearHand };

// Coordinates and pivots are in authoring pixels, relative to a 64 px body cell.
struct AuthoredSpriteDraw
{
    AuthoredSpritePart part{};
    SDL_FRect source{};
    SDL_FPoint position{}; // world-space pivot inside the character's tile
    SDL_FPoint pivot{};    // local pivot inside the source rectangle
    double angle{};
};

struct AuthoredWeaponComposition
{
    std::array<AuthoredSpriteDraw, 6> draws{};
    std::size_t count{};
};

class AuthoredWeaponLayer
{
public:
    AuthoredWeaponLayer();
    ~AuthoredWeaponLayer();
    AuthoredWeaponLayer(AuthoredWeaponLayer &&) noexcept;
    AuthoredWeaponLayer &operator=(AuthoredWeaponLayer &&) noexcept;
    bool load(SDL_Renderer *, const std::filesystem::path &assetRoot);
    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(data_); }
    [[nodiscard]] AuthoredWeaponComposition compose(const AuthoredWeaponPoseFrame &) const;
    void draw(SDL_Renderer *, const SDL_FRect &bodyBounds, const AuthoredWeaponComposition &) const;
private:
    struct Data;
    std::unique_ptr<Data> data_;
};
