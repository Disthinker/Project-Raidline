#pragma once

#include <array>
#include <string>
#include <vector>
#include "ui_foundation.h"
#include "item_definition.h"
#include "raid_action.h"

class UiTextRenderer;
struct WeaponLightProjection;
struct NightVisionProjection;
[[nodiscard]] std::string_view uiNightVisionStateLabel(const NightVisionProjection &) noexcept;
[[nodiscard]] std::string_view uiWeaponLightStateLabel(const WeaponLightProjection &) noexcept;
[[nodiscard]] std::string_view uiWeaponFireModeName(WeaponFireMode, UiLanguage) noexcept;
[[nodiscard]] std::string_view uiPlayerActionLabel(const RaidAction &) noexcept;
// Values already authorized by the current GameSession queries. No actor or
// inventory scan belongs to the painter; hidden map information has no field.
struct UiRaidHudProjection {
    std::string health, armor, weapon, ammunition, condition, clock, phase;
    std::string medical, objective, crisis, signal, action, feedback, light, nightVision;
    std::array<std::string, 3> slots;
    std::size_t activeSlot{};
    float healthFraction{}, actionFraction{};
    bool danger{}, actionActive{};
};
struct UiRaidHudLayout {
    SDL_FRect mission, vitals, weapon, action, feedback, light;
    std::array<SDL_FRect, 3> slots;
};
// One logical-coordinate projection reserves the complete bottom HUD bounds.
// Compact feedback performs its dp conversion once before joining these lanes.
inline constexpr float kUiGameplayHudGap=8;
struct UiGameplayBottomHudContent {
    bool base{}, light{}, melee{}, transientFeedback{}, checkpointDiagnostics{};
    std::string_view feedback;
};
struct UiGameplayBottomHudLayout {
    SDL_FRect vitals{},weapon{},action{},medical{},condition{},interaction{},light{},melee{},throwableHint{};
    std::array<SDL_FRect,3> slots{};
    UiCompactFeedbackLayout feedback{};
};
[[nodiscard]] UiGameplayBottomHudLayout layoutUiGameplayBottomHud(
    SDL_Renderer *,UiTextRenderer &,const UiGameplayBottomHudContent &);
[[nodiscard]] UiRaidHudLayout layoutUiRaidHud() noexcept;
void renderUiRaidHud(SDL_Renderer *, UiTextRenderer &, const UiRaidHudProjection &);
[[nodiscard]] SDL_FRect uiAuxiliaryHintRect(std::size_t lines, float x, float y,
                                          float width) noexcept;
[[nodiscard]] std::string uiRaidInteriorMapTitle(UiLanguage language,
    std::string_view displayName, bool layoutKnown);
