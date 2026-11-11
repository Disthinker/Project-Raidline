#pragma once

#include "raid_visibility.h"
#include <SDL3/SDL.h>
#include <array>
#include <vector>

using UiIlluminationTriangle = std::array<Vec2, 3>;

[[nodiscard]] std::vector<UiIlluminationTriangle> clipUiIlluminatedObservation(
    const RaidVisibilityProjection &, Rect visible);

void drawUiThrownIllumination(SDL_Renderer *,
    const std::array<RaidVisibilityProjection, 2> &, Rect visible,
    float glareSeconds = 0.0F);
