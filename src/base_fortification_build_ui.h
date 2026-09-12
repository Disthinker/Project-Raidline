#pragma once
#include "base_fortification_placement.h"
#include <SDL3/SDL_rect.h>
#include <array>

namespace fortification_ui
{
inline constexpr SDL_FRect category{88, 648, 262, 28};
inline constexpr SDL_FRect bar{70, 566, 1140, 134};
inline constexpr SDL_FRect purchase{88, 608, 126, 34};
inline constexpr SDL_FRect owned{224, 608, 126, 34};
inline constexpr SDL_FRect previous{928, 568, 40, 18};
inline constexpr SDL_FRect next{978, 568, 40, 18};
inline constexpr SDL_FRect zoom{1050, 608, 136, 34};
inline constexpr std::size_t pageSize = 4;
inline SDL_FRect card(std::size_t i) { return {370 + 168 * static_cast<float>(i), 588, 158, 82}; }
inline SDL_FRect menuRow(Vec2 at, unsigned row) { return {at.x, at.y + 34 * row, 260, 34}; }
}

// Transient client selection/cache only; no authoritative durability/materials.
struct BaseFortificationBuildUi
{
    bool category{};
    bool submitted{};
    std::optional<FortificationInstanceId> placing;
    std::optional<FortificationInstanceId> menu;
    Vec2 menuAt{};
    std::optional<std::size_t> hover;
    std::array<BaseDefensePosition, 4> slots;
    std::array<std::optional<FortificationPlacementPlan>, 4> previews;
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t geometryRevision{};
    Vec2 player{};
    bool warning{};
    std::size_t proofCount{}; // diagnostics for bounded preview regression tests
};
