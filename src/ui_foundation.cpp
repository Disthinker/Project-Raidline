#include "ui_foundation.h"
#include "ui_text_renderer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

void uiFinalPanel(SDL_Renderer *renderer, const SDL_FRect &bounds, bool warning) {
  if (!renderer || bounds.w <= 0 || bounds.h <= 0) return;
  SDL_BlendMode previous{};
  SDL_GetRenderDrawBlendMode(renderer, &previous);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 21, 29, 31, 244);
  SDL_RenderFillRect(renderer, &bounds);
  SDL_SetRenderDrawColor(renderer, warning ? 199 : 77, warning ? 137 : 112,
                         warning ? 101 : 108, 255);
  SDL_RenderRect(renderer, &bounds);
  const SDL_FRect accent{bounds.x, bounds.y, 3, bounds.h};
  SDL_RenderFillRect(renderer, &accent);
  SDL_SetRenderDrawBlendMode(renderer, previous);
}

void uiFinalText(SDL_Renderer *renderer, UiTextRenderer &text,
                 const SDL_FRect &bounds, std::string_view label, int size,
                 int maximumLines, SDL_Color color) {
  if (!renderer || label.empty()) return;
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  text.renderBox(renderer, bounds, label,
                 {size, UiTextAlign::Left, maximumLines});
}

void uiFinalTextAt(SDL_Renderer *renderer, UiTextRenderer &text, float x, float y,
                   std::string_view label, float width, int size,
                   int maximumLines, SDL_Color color) {
  if (width <= 0) width = std::max(1.0F, 1260.0F - x);
  const float lineHeight = std::ceil(size * 1.4F);
  const float height = maximumLines > 0 ? lineHeight * maximumLines
                                       : std::max(lineHeight, 708.0F - y);
  uiFinalText(renderer, text, {x, y, width, height}, label, size, maximumLines,
              color);
}

SDL_FRect uiFinalWorldLabelBounds(const SDL_FRect &labelBounds,
                                 const SDL_FRect &avatarBounds,
                                 const SDL_FRect &visibleWorld,
                                 const std::function<bool(const SDL_FRect &)> &candidateAllowed) {
  const auto valid = [](const SDL_FRect &rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.w) && std::isfinite(rect.h) &&
           rect.w > 0 && rect.h > 0;
  };
  const auto intersects = [](const SDL_FRect &first, const SDL_FRect &second) {
    return first.x < second.x + second.w && first.x + first.w > second.x &&
           first.y < second.y + second.h && first.y + first.h > second.y;
  };
  const auto backplate = [](const SDL_FRect &rect) {
    return SDL_FRect{rect.x - 4, rect.y - 2, rect.w + 8, rect.h + 2};
  };
  if (!valid(labelBounds) || !valid(avatarBounds) || !valid(visibleWorld) ||
      !intersects(labelBounds, visibleWorld) ||
      !intersects(avatarBounds, visibleWorld) ||
      !intersects(backplate(labelBounds), avatarBounds)) return labelBounds;

  const std::array<SDL_FRect, 4> candidates{{
      {avatarBounds.x + avatarBounds.w + 8, labelBounds.y,
       labelBounds.w, labelBounds.h},
      {avatarBounds.x - labelBounds.w - 8, labelBounds.y,
       labelBounds.w, labelBounds.h},
      {labelBounds.x, avatarBounds.y + avatarBounds.h + 6,
       labelBounds.w, labelBounds.h},
      {labelBounds.x, avatarBounds.y - labelBounds.h - 6,
       labelBounds.w, labelBounds.h}}};
  for (const auto &candidate : candidates) {
    const auto back = backplate(candidate);
    if (back.x >= visibleWorld.x && back.y >= visibleWorld.y &&
        back.x + back.w <= visibleWorld.x + visibleWorld.w &&
        back.y + back.h <= visibleWorld.y + visibleWorld.h &&
        !intersects(back, avatarBounds) &&
        (!candidateAllowed || candidateAllowed(candidate))) return candidate;
  }
  return labelBounds;
}

void uiFinalWorldLabel(SDL_Renderer *renderer, UiTextRenderer &text, float x,
                       float y, std::string_view label,
                       const SDL_FRect &avatarBounds,
                       const SDL_FRect &visibleWorld) {
  if (!renderer || label.empty() || !std::isfinite(x) || !std::isfinite(y)) return;
  const auto localized = localizeUiText(text.language(), label);
  const float width = std::clamp(text.measure(localized, 12), 24.0F, 280.0F);
  const auto bounds = uiFinalWorldLabelBounds({x, y, width, 18}, avatarBounds,
                                             visibleWorld);
  SDL_BlendMode blend{};
  SDL_GetRenderDrawBlendMode(renderer, &blend);
  Uint8 r{}, g{}, b{}, a{};
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 17, 26, 27, 205);
  const SDL_FRect back{bounds.x - 4, bounds.y - 2, bounds.w + 8, bounds.h + 2};
  SDL_RenderFillRect(renderer, &back);
  uiFinalText(renderer, text, bounds, label, 12, 1, {222, 231, 226, 255});
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_SetRenderDrawBlendMode(renderer, blend);
}

void uiFinalScrollableText(SDL_Renderer *renderer, UiTextRenderer &text,
                           const SDL_FRect &bounds, std::string_view label,
                           float &lineOffset, int size) {
  if (!renderer || bounds.w <= 16 || bounds.h < size * 1.4F) return;
  const auto localized = localizeUiText(text.language(), label);
  const auto lines = wrapUiText(localized, bounds.w - 10, 0,
      [&](auto value) { return text.measure(value, size); });
  const float height = std::ceil(size * 1.4F);
  const auto fitting = static_cast<std::size_t>(std::max(1.0F, std::floor(bounds.h / height)));
  const bool scroll = lines.size() > fitting;
  const std::size_t visible = scroll ? std::max(std::size_t{1}, fitting - 1) : fitting;
  const std::size_t maximum = lines.size() > visible ? lines.size() - visible : 0;
  lineOffset = std::clamp(lineOffset, 0.0F, static_cast<float>(maximum));
  const auto first = static_cast<std::size_t>(lineOffset);
  for (std::size_t i = 0; i < visible && first + i < lines.size(); ++i)
    uiFinalText(renderer, text, {bounds.x, bounds.y + i * height, bounds.w - 10, height},
        lines[first + i].text, size, 1);
  if (scroll) {
    const auto hint = text.language() == UiLanguage::SimplifiedChinese
        ? "滚轮查看完整信息" : "SCROLL FOR FULL MESSAGE";
    uiFinalText(renderer, text, {bounds.x, bounds.y + bounds.h - 14, bounds.w - 10, 14},
        hint, 10, 1, {154, 191, 179, 255});
    SDL_SetRenderDrawColor(renderer, 113, 191, 179, 255);
    const float trackHeight = std::max(12.0F, bounds.h - 16);
    const float thumbHeight = std::max(8.0F, trackHeight * visible / lines.size());
    const SDL_FRect thumb{bounds.x + bounds.w - 3,
        bounds.y + (trackHeight - thumbHeight) * first / std::max(std::size_t{1}, maximum), 2, thumbHeight};
    SDL_RenderFillRect(renderer, &thumb);
  }
}

namespace {
bool knownSize(UiWindowSize size) {
  return std::any_of(kUiWindowSizes.begin(), kUiWindowSizes.end(),
                     [size](auto s) {
                       return s.width == size.width && s.height == size.height;
                     });
}
std::size_t nextUtf8(std::string_view text, std::size_t i) {
  const auto c = static_cast<unsigned char>(text[i]);
  const std::size_t n = c < 0x80             ? 1
                        : (c & 0xe0) == 0xc0 ? 2
                        : (c & 0xf0) == 0xe0 ? 3
                        : (c & 0xf8) == 0xf0 ? 4
                                             : 1;
  return std::min(text.size(), i + n);
}
} // namespace
UiClientSettings
loadUiClientSettings(const std::filesystem::path &path) noexcept {
  UiClientSettings result;
  try {
    std::ifstream in(path);
    if (!in)
      return result;
    nlohmann::json document;
    in >> document;
    if (!document.is_object())
      return result;
    if (document.contains("language") && document["language"].is_string())
      result.language = document["language"] == "en-US"
                            ? UiLanguage::English
                            : UiLanguage::SimplifiedChinese;
    if (document.contains("window") && document["window"].is_object()) {
      const auto &w = document["window"];
      if (w.contains("width") && w["width"].is_number_integer() &&
          w.contains("height") && w["height"].is_number_integer()) {
        UiWindowSize candidate{w["width"].get<int>(), w["height"].get<int>()};
        if (knownSize(candidate))
          result.window = candidate;
      }
    }
  } catch (...) {
  }
  return result;
}
bool saveUiClientSettings(const std::filesystem::path &path,
                          const UiClientSettings &settings) noexcept {
  if (!knownSize(settings.window) || path.empty())
    return false;
  try {
    if (!path.parent_path().empty())
      std::filesystem::create_directories(path.parent_path());
    nlohmann::json document = nlohmann::json::object();
    {
      std::ifstream in(path);
      if (in) {
        auto previous = nlohmann::json::parse(in, nullptr, false);
        if (previous.is_object())
          document = std::move(previous);
      }
    }
    document["language"] = uiLanguageCode(settings.language);
    document["window"] = {{"width", settings.window.width},
                          {"height", settings.window.height}};
    auto temporary = path;
    temporary += ".tmp";
    {
      std::ofstream out(temporary, std::ios::trunc);
      if (!out)
        return false;
      out << document.dump(2);
      out.flush();
      if (!out.good())
        return false;
    }
    const auto oldUtf8 = temporary.u8string(), newUtf8 = path.u8string();
    const bool replaced =
        SDL_RenamePath(reinterpret_cast<const char *>(oldUtf8.c_str()),
                       reinterpret_cast<const char *>(newUtf8.c_str()));
    if (!replaced) {
      std::error_code ec;
      std::filesystem::remove(temporary, ec);
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}
bool uiWindowSizeAllowed(UiWindowSize s, int width, int height) noexcept {
  return knownSize(s) && s.width <= width && s.height <= height;
}
UiWindowSize safeUiWindowSize(UiWindowSize s, int width, int height) noexcept {
  if (uiWindowSizeAllowed(s, width, height))
    return s;
  for (auto i = kUiWindowSizes.rbegin(); i != kUiWindowSizes.rend(); ++i)
    if (uiWindowSizeAllowed(*i, width, height))
      return *i;
  // Keep even a small work area on-screen; the logical layout letterboxes.
  const float scale = std::min(
      {1.0F, std::max(1, width) / 1280.0F, std::max(1, height) / 720.0F});
  return {std::max(1, static_cast<int>(1280 * scale)),
          std::max(1, static_cast<int>(720 * scale))};
}
UiViewport UiViewport::fromWindow(int w, int h) noexcept {
  const float scale = std::max(
      0.001F, std::min(std::max(1, w) / 1280.0F, std::max(1, h) / 720.0F));
  return {scale, (w - 1280 * scale) * 0.5F, (h - 720 * scale) * 0.5F};
}
Vec2 UiViewport::toLogical(Vec2 p) const noexcept {
  return {(p.x - x) / scale, (p.y - y) / scale};
}
Vec2 UiViewport::toWindow(Vec2 p) const noexcept {
  return {p.x * scale + x, p.y * scale + y};
}
std::vector<UiTextLine> wrapUiText(std::string_view text, float width,
                                   int maximumLines,
                                   const UiMeasureText &measure) {
  std::vector<UiTextLine> lines;
  if (text.empty() || width <= 0)
    return lines;
  std::string current;
  bool omitted = false;
  for (std::size_t i = 0; i < text.size();) {
    const auto end = nextUtf8(text, i);
    const auto cp = text.substr(i, end - i);
    if (cp != "\n" && measure(cp) > width) {
      omitted = true;
      break;
    }
    if (cp == "\n") {
      lines.push_back({current, measure(current)});
      current.clear();
    } else {
      const std::string candidate = current + std::string(cp);
      if (!current.empty() && measure(candidate) > width) {
        const auto space = current.find_last_of(' ');
        if (space != std::string::npos && space > 0 &&
            space + 1 < current.size() &&
            measure(current.substr(space + 1) + std::string(cp)) <= width) {
          const auto prefix = current.substr(0, space + 1);
          lines.push_back({prefix, measure(prefix)});
          current.erase(0, space + 1);
        } else {
          lines.push_back({current, measure(current)});
          current.clear();
        }
      }
      if (maximumLines > 0 && static_cast<int>(lines.size()) >= maximumLines) {
        omitted = true;
        break;
      }
      current.append(cp);
    }
    i = end;
    if (maximumLines > 0 && static_cast<int>(lines.size()) >= maximumLines &&
        i < text.size()) {
      omitted = true;
      break;
    }
  }
  if (!current.empty() &&
      (maximumLines <= 0 || static_cast<int>(lines.size()) < maximumLines))
    lines.push_back({current, measure(current)});
  if (omitted && !lines.empty()) {
    auto &last = lines.back();
    while (!last.text.empty() && measure(last.text + "…") > width) {
      std::size_t previous = 0;
      for (std::size_t i = 0; i < last.text.size();) {
        previous = i;
        i = nextUtf8(last.text, i);
      }
      last.text.resize(previous);
    }
    if (measure("…") <= width)
      last.text += "…";
    last.width = measure(last.text);
  }
  return lines;
}
SDL_FRect uiSettingsLanguageButton() noexcept { return {480, 292, 320, 44}; }
SDL_FRect uiSettingsSizeButton(std::size_t i) noexcept {
  return {352 + static_cast<float>(i) * 200, 394, 176, 44};
}
SDL_FRect uiSettingsBackButton() noexcept { return {500, 566, 280, 46}; }
bool uiContains(const SDL_FRect &b, Vec2 p) noexcept {
  return p.x >= b.x && p.y >= b.y && p.x < b.x + b.w && p.y < b.y + b.h;
}
float uiHoverAmount(float time) noexcept {
  const float t = std::clamp(time / 0.12F, 0.0F, 1.0F);
  return t * t * (3 - 2 * t);
}
