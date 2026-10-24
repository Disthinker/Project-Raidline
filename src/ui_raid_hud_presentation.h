#pragma once

#include <array>
#include <string>
#include <vector>
#include "ui_foundation.h"

class UiTextRenderer;
// Values already authorized by the current GameSession queries. No actor or
// inventory scan belongs to the painter; hidden map information has no field.
struct UiRaidHudProjection {
    std::string health, armor, weapon, ammunition, condition, clock, phase;
    std::string medical, objective, crisis, signal, action, feedback, light;
    std::array<std::string, 3> slots;
    std::size_t activeSlot{};
    float healthFraction{}, actionFraction{};
    bool danger{}, actionActive{};
};
struct UiRaidHudLayout {
    SDL_FRect mission, vitals, weapon, action, feedback, light;
    std::array<SDL_FRect, 3> slots;
};
[[nodiscard]] UiRaidHudLayout layoutUiRaidHud() noexcept;
void renderUiRaidHud(SDL_Renderer *, UiTextRenderer &, const UiRaidHudProjection &);
[[nodiscard]] SDL_FRect uiAuxiliaryHintRect(std::size_t lines, float x, float y,
                                          float width) noexcept;
[[nodiscard]] std::string uiRaidInteriorMapTitle(UiLanguage language,
    std::string_view displayName, bool layoutKnown);
