#pragma once

#include "player_motion.h"
#include "texture.h"
#include "vec2.h"
#include <SDL3/SDL.h>
#include <cmath>

// The authored sheet has six clip rows per facing, eight 64 px cells per row.
inline constexpr int kPlayerAtlasCell = 64;
inline constexpr int kPlayerAtlasRowsPerFacing = 6;
inline constexpr auto kPlayerAtlasPath = "characters/player/authored_motion_v1/atlas.png";

inline SDL_FRect playerAtlasFrame(PlayerMotionFrame frame) noexcept
{
    const int row = static_cast<int>(frame.clip) + (frame.left ? kPlayerAtlasRowsPerFacing : 0);
    // Each walk-to-run row holds a two-frame transition for each of the
    // four possible walk poses on that support foot.
    std::size_t column = frame.index;
    if (frame.clip == PlayerMotionClip::WalkToRunNear || frame.clip == PlayerMotionClip::WalkToRunFar)
        column += 2U * frame.entryPhase;
    return {static_cast<float>(column * kPlayerAtlasCell),
            static_cast<float>(row * kPlayerAtlasCell), kPlayerAtlasCell, kPlayerAtlasCell};
}

inline SDL_FRect playerAvatarBounds(Vec2 position, Vec2 bodySize) noexcept
{
    // The former 80 px sprite's feet ended 35 px below the body center.
    // The authored standing figure is 49 px tall; 90/64 keeps its on-screen
    // height at 69 px and aligns the lower edge of contact row 60 to that mark.
    constexpr float size = 90.0F;
    constexpr float contactEdge = 61.0F * size / kPlayerAtlasCell;
    return {std::round(position.x + bodySize.x * 0.5F - size * 0.5F),
            std::round(position.y + bodySize.y * 0.5F + 35.0F - contactEdge),
            size, size};
}

inline Texture loadPlayerAtlas(SDL_Renderer *renderer, const std::filesystem::path &assetRoot)
{
    // The preview is the left idle frame. Measure its visible extent once while
    // loading; retain the whole texture for world animation and optical passes.
    constexpr SDL_Rect previewCell{0, kPlayerAtlasRowsPerFacing * kPlayerAtlasCell,
                                   kPlayerAtlasCell, kPlayerAtlasCell};
    auto atlas = loadInventoryTexture(renderer, assetRoot / kPlayerAtlasPath, previewCell);
    float width{}, height{};
    if (!atlas.valid() || !SDL_GetTextureSize(atlas.get(), &width, &height) ||
        width != 8 * kPlayerAtlasCell || height != 2 * kPlayerAtlasRowsPerFacing * kPlayerAtlasCell ||
        !SDL_SetTextureScaleMode(atlas.get(), SDL_SCALEMODE_NEAREST))
        return {};
    return atlas;
}
