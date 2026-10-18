#include "app.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace {
void color(SDL_Renderer *r, SDL_Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
constexpr SDL_Color text{232, 237, 242, 255}, muted{164, 177, 190, 255};
void panel(SDL_Renderer *r, SDL_FRect bounds) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  color(r, {18, 24, 33, 250});
  SDL_RenderFillRect(r, &bounds);
  color(r, {62, 77, 94, 255});
  SDL_RenderRect(r, &bounds);
  const SDL_FRect accent{bounds.x + 32, bounds.y + 24, 40, 3};
  color(r, {113, 191, 179, 255});
  SDL_RenderFillRect(r, &accent);
}
} // namespace
UiViewport App::windowUiViewport() const noexcept {
  int w = 1280, h = 720;
  if (window_)
    SDL_GetWindowSize(window_, &w, &h);
  return UiViewport::fromWindow(w, h);
}
UiWindowSize App::availableUiWindowArea() const noexcept {
  SDL_Rect area{};
  const auto display =
      window_ ? SDL_GetDisplayForWindow(window_) : SDL_GetPrimaryDisplay();
  if (!display || !SDL_GetDisplayUsableBounds(display, &area))
    return {1280, 720};
  int top = 32, left = 8, bottom = 8, right = 8;
  if (window_)
    SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right);
  return {std::max(1, area.w - left - right),
          std::max(1, area.h - top - bottom)};
}
void App::applyUiWindowSize(std::size_t index) {
  if (index >= kUiWindowSizes.size() || !window_ || !renderer_)
    return;
  const auto requested = kUiWindowSizes[index];
  const auto area = availableUiWindowArea();
  if (!uiWindowSizeAllowed(requested, area.width, area.height)) {
    uiMessage_ = "WINDOW SIZE UNAVAILABLE ON THIS DISPLAY";
    return;
  }
  int oldWidth{}, oldHeight{};
  int oldX{}, oldY{};
  SDL_GetWindowSize(window_, &oldWidth, &oldHeight);
  SDL_GetWindowPosition(window_, &oldX, &oldY);
  SDL_Rect workArea{};
  const bool haveWorkArea =
      SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &workArea);
  if (!SDL_SetWindowSize(window_, requested.width, requested.height)) {
    uiMessage_ = "WINDOW CHANGE FAILED - PREVIOUS SIZE KEPT";
    return;
  }
  SDL_SyncWindow(window_);
  int actualWidth{}, actualHeight{};
  SDL_GetWindowSize(window_, &actualWidth, &actualHeight);
  if (actualWidth != requested.width || actualHeight != requested.height) {
    SDL_SetWindowSize(window_, oldWidth, oldHeight);
    uiMessage_ = "WINDOW CHANGE FAILED - PREVIOUS SIZE KEPT";
    return;
  }
  if (haveWorkArea) {
    int top{}, left{}, bottom{}, right{};
    SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right);
    const int minX = workArea.x + left, minY = workArea.y + top;
    const int maxX = workArea.x + workArea.w - requested.width - right;
    const int maxY = workArea.y + workArea.h - requested.height - bottom;
    if (maxX < minX || maxY < minY ||
        !SDL_SetWindowPosition(window_, std::clamp(oldX, minX, maxX),
                               std::clamp(oldY, minY, maxY))) {
      SDL_SetWindowSize(window_, oldWidth, oldHeight);
      SDL_SetWindowPosition(window_, oldX, oldY);
      uiMessage_ = "WINDOW CHANGE FAILED - PREVIOUS SIZE KEPT";
      return;
    }
    SDL_SyncWindow(window_);
  }
  if (!SDL_SetRenderLogicalPresentation(renderer_, 1280, 720,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    SDL_SetWindowSize(window_, oldWidth, oldHeight);
    uiMessage_ = "WINDOW CHANGE FAILED - PREVIOUS SIZE KEPT";
    return;
  }
  uiWindowSize_ = requested;
  // Only uncommitted client gestures are cancelled. No inventory command is
  // sent.
  inventoryInteraction_.cancelPointerGesture();
  profileInventoryInteraction_.cancelPointerGesture();
  profileAssetSelection_.reset();
  profileContextMenu_.reset();
  pendingInventoryUiEvents_.clear();
  pendingProfileRightClicks_.clear();
  pendingBaseClicks_.clear();
  pendingBaseRightClicks_.clear();
  pendingRelativeAimMotion_ = {};
  input_.suppressPrimaryPointerUntilRelease();
  uiHoveredButton_ = -1;
  float x{}, y{};
  SDL_GetMouseState(&x, &y);
  pointerWorldPosition_ = windowUiViewport().toLogical({x, y});
  int outputWidth{}, outputHeight{};
  SDL_GetRenderOutputSize(renderer_, &outputWidth, &outputHeight);
  uiTextRenderer_.setRasterScale(
      std::min(outputWidth / 1280.0F, outputHeight / 720.0F));
  auto settings = loadUiClientSettings(settingsPath_);
  settings.window = requested;
  settings.language = uiTextRenderer_.language();
  uiMessage_ = saveUiClientSettings(settingsPath_, settings)
                   ? "WINDOW SIZE UPDATED"
                   : "WINDOW CHANGED - SETTINGS SAVE FAILED";
}
void App::renderFoundationButton(int id, SDL_FRect bounds,
                                 std::string_view label, bool enabled,
                                 bool danger, bool selected,
                                 std::string_view hint) {
  const bool hover = windowHasInputFocus_ && uiPointerInside_ &&
                     pointerWorldPosition_ &&
                     uiContains(bounds, *pointerWorldPosition_);
  if (hover && uiHoveredButton_ != id) {
    uiHoveredButton_ = id;
    uiHoverStarted_ = uiClockSeconds_;
  }
  if (!hover && uiHoveredButton_ == id)
    uiHoveredButton_ = -1;
  const float amount =
      hover ? uiHoverAmount(uiClockSeconds_ - uiHoverStarted_) : 0;
  SDL_Color fill = enabled ? (danger ? SDL_Color{76, 39, 42, 255}
                                     : SDL_Color{37, 50, 65, 255})
                           : SDL_Color{29, 35, 44, 255};
  fill.r = static_cast<Uint8>(fill.r + amount * 12);
  fill.g = static_cast<Uint8>(fill.g + amount * 15);
  fill.b = static_cast<Uint8>(fill.b + amount * 17);
  if (hover && uiPointerPressed_ && enabled) {
    fill.r /= 2;
    fill.g /= 2;
    fill.b /= 2;
  }
  color(renderer_, fill);
  SDL_RenderFillRect(renderer_, &bounds);
  color(renderer_, hover || selected ? SDL_Color{113, 191, 179, 255}
                                     : SDL_Color{65, 81, 99, 255});
  SDL_RenderRect(renderer_, &bounds);
  if (selected) {
    const SDL_FRect mark{bounds.x + 8, bounds.y + 8, 3, bounds.h - 16};
    color(renderer_, {113, 191, 179, 255});
    SDL_RenderFillRect(renderer_, &mark);
  }
  color(renderer_, enabled ? text : muted);
  // Magnify the label inside the stable button, never its hit rectangle.
  const float zoom = 1.0F + (enabled ? 0.04F * amount : 0.0F);
  float scaleX = 1, scaleY = 1;
  SDL_GetRenderScale(renderer_, &scaleX, &scaleY);
  SDL_SetRenderScale(renderer_, scaleX * zoom, scaleY * zoom);
  uiTextRenderer_.renderBox(renderer_,
                            {(bounds.x + 12) / zoom,
                             (bounds.y + bounds.h * 0.5F) / zoom - 12.6F,
                             (bounds.w - 24) / zoom, 25.2F},
                            label, {18, UiTextAlign::Center, 1});
  SDL_SetRenderScale(renderer_, scaleX, scaleY);
  if (hover && uiClockSeconds_ - uiHoverStarted_ >= 0.4F)
    uiHoveredHint_ = hint;
}
void App::renderFoundationSettings(bool paused) {
  static_cast<void>(paused);
  panel(renderer_, {280, 56, 720, 608});
  color(renderer_, text);
  uiTextRenderer_.renderBox(renderer_, {320, 100, 640, 46}, "SETTINGS",
                            {28, UiTextAlign::Left});
  color(renderer_, muted);
  uiTextRenderer_.renderBox(renderer_, {320, 160, 640, 28},
                            "WASD MOVE  SHIFT SPRINT  E INTERACT", {16});
  uiTextRenderer_.renderBox(renderer_, {320, 190, 640, 28},
                            "LMB FIRE | RMB AIM | R RELOAD | V MELEE", {16});
  uiTextRenderer_.renderBox(
      renderer_, {320, 220, 640, 28},
      "TAB INVENTORY | M MAP | 4 THROWABLES | 5 MED | U LIGHT", {16});
  color(renderer_, text);
  uiTextRenderer_.renderBox(renderer_, {320, 258, 640, 28}, "LANGUAGE", {18});
  renderFoundationButton(
      100, uiSettingsLanguageButton(),
      fmt::format("LANGUAGE: {}",
                  uiLanguageDisplayName(uiTextRenderer_.language())),
      true, false, false, "SWITCH BETWEEN CHINESE AND ENGLISH");
  color(renderer_, text);
  uiTextRenderer_.renderBox(renderer_, {320, 354, 640, 28}, "WINDOW SIZE",
                            {18});
  const auto area = availableUiWindowArea();
  for (std::size_t i = 0; i < kUiWindowSizes.size(); ++i) {
    const auto size = kUiWindowSizes[i];
    if (!uiWindowSizeAllowed(size, area.width, area.height))
      continue;
    renderFoundationButton(110 + static_cast<int>(i), uiSettingsSizeButton(i),
                           fmt::format("{} × {}", size.width, size.height),
                           true, false,
                           uiWindowSize_.width == size.width &&
                               uiWindowSize_.height == size.height,
                           "WINDOW SIZE DOES NOT CHANGE WORLD VIEW");
  }
  renderFoundationButton(120, uiSettingsBackButton(), "BACK", true, false,
                         false, "ESC RETURNS TO THE PREVIOUS MENU");
  color(renderer_, muted);
  const std::string_view message =
      !uiHoveredHint_.empty() ? std::string_view{uiHoveredHint_}
      : !uiMessage_.empty()
          ? std::string_view{uiMessage_}
          : "ONLY WINDOW SIZES THAT FIT THIS DISPLAY ARE SHOWN";
  uiTextRenderer_.renderBox(renderer_, {320, 458, 640, 82}, message, {16});
}
void App::renderFoundationMenu(bool paused) {
  uiHoveredHint_.clear();
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  const SDL_FRect screen{0, 0, 1280, 720};
  color(renderer_,
        paused ? SDL_Color{4, 7, 11, 205} : SDL_Color{10, 15, 22, 255});
  SDL_RenderFillRect(renderer_, &screen);
  if (paused ? pauseMenu_.settingsOpen() : settingsOpen_) {
    renderFoundationSettings(paused);
    return;
  }
  panel(renderer_, {320, 80, 640, 580});
  color(renderer_, muted);
  uiTextRenderer_.renderBox(renderer_, {352, 126, 576, 30},
                            "PREPARE. SEARCH. RETURN.", {14});
  color(renderer_, text);
  uiTextRenderer_.renderBox(renderer_, {352, 169, 576, 48},
                            paused ? "PAUSED" : "PROJECT RAIDLINE", {28});
  color(renderer_, muted);
  uiTextRenderer_.renderBox(renderer_, {352, 230, 576, 70},
                            paused               ? "ESC TO CONTINUE"
                            : uiMessage_.empty() ? "CHOOSE HOW TO BEGIN"
                                                 : std::string_view{uiMessage_},
                            {16});
  if (paused) {
    constexpr std::array<const char *, 4> labels{
        "CONTINUE GAME", "SETTINGS", "EXIT TO MAIN MENU", "EXIT TO DESKTOP"};
    for (std::size_t i = 0; i < labels.size(); ++i)
      renderFoundationButton(
          200 + static_cast<int>(i), pauseMenuButton(i), labels[i], true,
          i >= 2, false,
          i >= 2 ? "ESC CONTINUES | RAID EXIT RESTORES GEAR; RESCUES PERSIST"
                 : "ESC TO CONTINUE");
    if (gameSession_.baseDefenseActive())
      renderFoundationButton(
          204, pauseMenuButton(4),
          baseDefenseAbandonArmed_ ? "CONFIRM ABANDON DEFENSE"
                                   : "ABANDON DEFENSE...",
          true, true, false, "CONFIRM ABANDON: PUBLIC SOFT LOSS");
    color(renderer_, muted);
    uiTextRenderer_.renderBox(
        renderer_, {352, 574, 576, 70},
        !uiHoveredHint_.empty() ? std::string_view{uiHoveredHint_}
        : gameSession_.baseDefenseActive()
            ? "CONFIRM ABANDON: PUBLIC SOFT LOSS"
            : "ESC CONTINUES | RAID EXIT RESTORES GEAR; RESCUES PERSIST",
        {14});
  } else {
    const std::array<const char *, 4> labels{
        "CONTINUE GAME",
        newGameOverwriteArmed_ ? "CONFIRM NEW GAME" : "NEW GAME", "SETTINGS",
        "EXIT"};
    for (std::size_t i = 0; i < labels.size(); ++i) {
      const bool enabled = i != 0 || gameSession_.hasSavedProfile();
      const char *hint = !enabled ? "NO SAVED GAME. START A NEW GAME."
                         : i == 1 ? (gameSession_.hasSavedProfile()
                                         ? "PRESS NEW GAME AGAIN TO OVERWRITE"
                                         : "CREATE A NEW SURVIVOR PROFILE")
                         : i == 2 ? "CHANGE LANGUAGE AND WINDOW SIZE"
                                  : "CHOOSE HOW TO BEGIN";
      renderFoundationButton(
          300 + static_cast<int>(i), mainMenuButton(i), labels[i], enabled,
          i == 3 || (i == 1 && newGameOverwriteArmed_), false, hint);
    }
    color(renderer_, muted);
    uiTextRenderer_.renderBox(renderer_, {352, 592, 576, 48}, uiHoveredHint_,
                              {14});
  }
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}
