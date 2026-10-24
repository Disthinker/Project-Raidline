#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <SDL3/SDL.h>
#include "ui_localization.h"
#include "vec2.h"

// Client-only presentation. World cameras and inventory coordinates remain 1280x720.
struct UiWindowSize { int width{1280}; int height{720}; };
inline constexpr std::array<UiWindowSize, 3> kUiWindowSizes{{{1280,720},{1600,900},{1920,1080}}};
struct UiClientSettings { UiLanguage language{UiLanguage::SimplifiedChinese}; UiWindowSize window{}; };
[[nodiscard]] UiClientSettings loadUiClientSettings(const std::filesystem::path &path) noexcept;
[[nodiscard]] bool saveUiClientSettings(const std::filesystem::path &path, const UiClientSettings &settings) noexcept;
[[nodiscard]] bool uiWindowSizeAllowed(UiWindowSize size, int availableWidth, int availableHeight) noexcept;
[[nodiscard]] UiWindowSize safeUiWindowSize(UiWindowSize requested, int availableWidth, int availableHeight) noexcept;

struct UiViewport {
    float scale{1}; float x{}; float y{};
    [[nodiscard]] static UiViewport fromWindow(int width, int height) noexcept;
    [[nodiscard]] Vec2 toLogical(Vec2 point) const noexcept;
    [[nodiscard]] Vec2 toWindow(Vec2 point) const noexcept;
};
enum class UiTextAlign { Left, Center, Right };
struct UiTextStyle { int size{18}; UiTextAlign align{UiTextAlign::Left}; int maximumLines{0}; };
struct UiTextLine { std::string text; float width{}; };
using UiMeasureText = std::function<float(std::string_view)>;
[[nodiscard]] std::vector<UiTextLine> wrapUiText(std::string_view text, float width, int maximumLines, const UiMeasureText &measure);

// Both drawing and hit-testing consume these exact rectangles.
[[nodiscard]] SDL_FRect uiSettingsLanguageButton() noexcept;
[[nodiscard]] SDL_FRect uiSettingsSizeButton(std::size_t index) noexcept;
[[nodiscard]] SDL_FRect uiSettingsBackButton() noexcept;
[[nodiscard]] bool uiContains(const SDL_FRect &bounds, Vec2 point) noexcept;
[[nodiscard]] float uiHoverAmount(float elapsedSeconds) noexcept;

class UiTextRenderer;
// Small client paint helpers shared by the remaining legacy surfaces. They do
// not own input, domain state or a separate layout coordinate system.
void uiFinalPanel(SDL_Renderer *renderer, const SDL_FRect &bounds,
                  bool warning = false);
void uiFinalText(SDL_Renderer *renderer, UiTextRenderer &text,
                 const SDL_FRect &bounds, std::string_view label,
                 int size = 14, int maximumLines = 1,
                 SDL_Color color = {222, 231, 226, 255});
void uiFinalTextAt(SDL_Renderer *renderer, UiTextRenderer &text, float x, float y,
                   std::string_view label, float width = 0, int size = 14,
                   int maximumLines = 1,
                   SDL_Color color = {222, 231, 226, 255});
// World coordinates only. A hidden/offscreen label keeps its original position;
// this helper never grants visibility or changes the caller's render clipping.
[[nodiscard]] SDL_FRect uiFinalWorldLabelBounds(
    const SDL_FRect &labelBounds, const SDL_FRect &avatarBounds = {},
    const SDL_FRect &visibleWorld = {},
    const std::function<bool(const SDL_FRect &)> &candidateAllowed = {});
void uiFinalWorldLabel(SDL_Renderer *renderer, UiTextRenderer &text, float x,
                       float y, std::string_view label,
                       const SDL_FRect &avatarBounds = {},
                       const SDL_FRect &visibleWorld = {});
void uiFinalScrollableText(SDL_Renderer *, UiTextRenderer &, const SDL_FRect &,
                           std::string_view, float &lineOffset, int size = 14);
