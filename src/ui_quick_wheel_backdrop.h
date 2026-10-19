#pragma once

#include <SDL3/SDL.h>
#include "texture.h"

// Captures this frame's already-obscured world, filters on render targets,
// and restores the caller's complete renderer state. No CPU pixel readback.
class UiQuickWheelBackdrop {
public:
    [[nodiscard]] bool begin(SDL_Renderer *);
    [[nodiscard]] bool compose();
    void reset() noexcept;
    [[nodiscard]] double lastComposeMilliseconds() const noexcept {return lastComposeMs_;}
private:
    void restore() noexcept;
    Texture scene_,low_,tiny_;
    SDL_Renderer *renderer_{};
    SDL_Texture *previousTarget_{};
    SDL_Rect viewport_{},clip_{};
    SDL_BlendMode blend_{};
    SDL_RendererLogicalPresentation logicalMode_{SDL_LOGICAL_PRESENTATION_DISABLED};
    int logicalWidth_{},logicalHeight_{};
    float scaleX_{1},scaleY_{1};
    Uint8 red_{},green_{},blue_{},alpha_{};
    bool clipped_{},active_{};
    double lastComposeMs_{};
};
