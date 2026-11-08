#pragma once

#include <SDL3/SDL.h>

#include "raid_camera.h"
#include "raid_visibility.h"

struct UiWeaponOpticView
{
    WeaponOpticViewProjection projection;
    Vec2 reticle;

    [[nodiscard]] bool hasOpaquePeriphery() const noexcept;
    [[nodiscard]] Rect sceneBounds() const noexcept;
    [[nodiscard]] bool includes(Vec2 world) const noexcept;
};

[[nodiscard]] UiWeaponOpticView makeUiWeaponOpticView(
    const WeaponOpticViewProjection &, Vec2 aim) noexcept;

// The scene texture and observation vertices share the same world projection.
[[nodiscard]] bool drawUiWeaponOpticObservation(
    SDL_Renderer *, SDL_Texture *, const UiWeaponOpticView &,
    const RaidVisibilityProjection &, float shade = 1.0F);

// The input texture has already been masked by authoritative observation.
[[nodiscard]] bool drawUiWeaponOpticLens(
    SDL_Renderer *, SDL_Texture *, const UiWeaponOpticView &);
