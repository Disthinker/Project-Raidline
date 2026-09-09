#pragma once
#include <SDL3/SDL_rect.h>

// One source for rendered controls and event hit testing (logical UI coordinates).
namespace base_siege_warning_layout {
inline constexpr SDL_FRect automatic{345, 474, 280, 52};
inline constexpr SDL_FRect manual{650, 474, 280, 52};
inline constexpr SDL_FRect close{898, 203, 38, 32};
inline constexpr SDL_FRect banner{365, 122, 550, 40};
}
