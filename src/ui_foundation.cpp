#include "ui_foundation.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

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
