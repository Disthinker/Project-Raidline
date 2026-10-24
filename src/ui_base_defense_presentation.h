#pragma once

#include "base_siege_domain.h"
#include "base_defense_state.h"
#include "ui_foundation.h"

struct UiBaseDefenseFacts {
    std::string title, progress, breach, nextWave, approaches;
    std::size_t total{}, processed{};
};
[[nodiscard]] UiBaseDefenseFacts projectUiBaseDefenseFacts(const BaseDefenseSnapshot &,Vec2 player,UiLanguage);
struct UiBaseDefenseResultFacts {
    std::string title, summary, losses, retained;
    bool defended{};
};
[[nodiscard]] UiBaseDefenseResultFacts projectUiBaseDefenseResultFacts(const BaseSiegeState &,UiLanguage);
[[nodiscard]] SDL_FRect uiBaseDefenseFeedbackRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseDefenseResultPanel() noexcept;
