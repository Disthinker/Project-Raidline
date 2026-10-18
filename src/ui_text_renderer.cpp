#include "ui_text_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
constexpr std::size_t kMaximumCachedLabels{1024U};

std::string cacheKey(std::string_view text, Uint8 red, Uint8 green, Uint8 blue,
                     Uint8 alpha) {
  std::string key;
  key.reserve(text.size() + 5U);
  key.push_back(static_cast<char>(red));
  key.push_back(static_cast<char>(green));
  key.push_back(static_cast<char>(blue));
  key.push_back(static_cast<char>(alpha));
  key.append(text);
  return key;
}

#if defined(_WIN32)
struct PlatformRenderedText {
  Texture texture;
  float width{};
  float height{};
};

std::wstring utf8ToWide(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const int required =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), nullptr, 0);
  if (required <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), wide.data(),
                          required) != required) {
    return {};
  }
  return wide;
}

PlatformRenderedText createWindowsTextTexture(SDL_Renderer *renderer,
                                              void *platformFont,
                                              std::string_view text, Uint8 red,
                                              Uint8 green, Uint8 blue,
                                              Uint8 alpha) {
  const std::wstring wide = utf8ToWide(text);
  if (wide.empty()) {
    return {};
  }

  HDC device = CreateCompatibleDC(nullptr);
  if (device == nullptr) {
    return {};
  }
  HGDIOBJ previousFont = SelectObject(device, static_cast<HFONT>(platformFont));
  SIZE measured{};
  if (!GetTextExtentPoint32W(device, wide.data(), static_cast<int>(wide.size()),
                             &measured)) {
    SelectObject(device, previousFont);
    DeleteDC(device);
    return {};
  }

  const int width = static_cast<int>(std::max(1L, measured.cx + 2L));
  const int height = static_cast<int>(std::max(1L, measured.cy + 2L));
  BITMAPINFO bitmapInfo{};
  bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth = width;
  bitmapInfo.bmiHeader.biHeight = -height;
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 32;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;
  void *maskPixels{};
  HBITMAP bitmap = CreateDIBSection(device, &bitmapInfo, DIB_RGB_COLORS,
                                    &maskPixels, nullptr, 0);
  if (bitmap == nullptr || maskPixels == nullptr) {
    SelectObject(device, previousFont);
    DeleteDC(device);
    return {};
  }
  HGDIOBJ previousBitmap = SelectObject(device, bitmap);
  PatBlt(device, 0, 0, width, height, BLACKNESS);
  SetBkMode(device, TRANSPARENT);
  SetTextColor(device, RGB(255, 255, 255));
  TextOutW(device, 1, 1, wide.data(), static_cast<int>(wide.size()));

  SDL_Surface *surface =
      SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
  if (surface == nullptr) {
    SelectObject(device, previousBitmap);
    DeleteObject(bitmap);
    SelectObject(device, previousFont);
    DeleteDC(device);
    return {};
  }
  const auto *mask = static_cast<const std::uint8_t *>(maskPixels);
  auto *destination = static_cast<std::uint32_t *>(surface->pixels);
  const int destinationStride =
      surface->pitch / static_cast<int>(sizeof(std::uint32_t));
  for (int y{}; y < height; ++y) {
    for (int x{}; x < width; ++x) {
      const std::size_t maskOffset =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x)) *
          4U;
      const Uint8 coverage =
          std::max(mask[maskOffset],
                   std::max(mask[maskOffset + 1U], mask[maskOffset + 2U]));
      destination[y * destinationStride + x] = SDL_MapSurfaceRGBA(
          surface, red, green, blue,
          static_cast<Uint8>(static_cast<unsigned int>(coverage) * alpha /
                             255U));
    }
  }

  PlatformRenderedText rendered{
      Texture{SDL_CreateTextureFromSurface(renderer, surface)},
      static_cast<float>(width), static_cast<float>(height)};
  SDL_DestroySurface(surface);
  SelectObject(device, previousBitmap);
  DeleteObject(bitmap);
  SelectObject(device, previousFont);
  DeleteDC(device);
  return rendered;
}
#endif
} // namespace

UiTextRenderer::~UiTextRenderer() { shutdown(); }

bool UiTextRenderer::initialize(SDL_Renderer *renderer) {
  shutdown();
  if (renderer == nullptr) {
    return false;
  }
#if defined(_WIN32)
  platformFont_ =
      CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                  DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  return platformFont_ != nullptr;
#else
  return true;
#endif
}

void UiTextRenderer::shutdown() noexcept {
  cache_.clear();
  widths_.clear();
  cacheUseSequence_ = 0U;
#if defined(_WIN32)
  for (auto &[size, font] : fonts_) {
    static_cast<void>(size);
    DeleteObject(static_cast<HFONT>(font));
  }
  fonts_.clear();
  if (platformFont_ != nullptr) {
    DeleteObject(static_cast<HFONT>(platformFont_));
    platformFont_ = nullptr;
  }
#endif
}

void UiTextRenderer::setLanguage(UiLanguage language) noexcept {
  if (language_ == language) {
    return;
  }
  language_ = language;
  cache_.clear();
  cacheUseSequence_ = 0U;
}

UiLanguage UiTextRenderer::language() const noexcept { return language_; }

void *UiTextRenderer::fontFor(int pixelSize) {
#if defined(_WIN32)
  if (pixelSize == 13)
    return platformFont_;
  auto found = fonts_.find(pixelSize);
  if (found != fonts_.end())
    return found->second;
  auto font = CreateFontW(-pixelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  if (font)
    fonts_.emplace(pixelSize, font);
  return font;
#else
  static_cast<void>(pixelSize);
  return nullptr;
#endif
}
void UiTextRenderer::setRasterScale(float scale) {
  scale = std::clamp(std::isfinite(scale) ? scale : 1.0F, 0.5F, 4.0F);
  if (std::abs(scale - rasterScale_) < 0.001F)
    return;
  rasterScale_ = scale;
  cache_.clear();
  widths_.clear();
#if defined(_WIN32)
  for (auto &[size, font] : fonts_) {
    static_cast<void>(size);
    DeleteObject(static_cast<HFONT>(font));
  }
#endif
  fonts_.clear();
}
float UiTextRenderer::measure(std::string_view text, int size) {
  if (text.empty())
    return 0;
  size = std::clamp(size, 8, 64);
  const int pixels = static_cast<int>(std::lround(size * rasterScale_));
  const std::string key = std::to_string(pixels) + ":" + std::string(text);
  if (auto found = widths_.find(key); found != widths_.end())
    return found->second;
  float width{};
#if defined(_WIN32)
  HDC device = CreateCompatibleDC(nullptr);
  auto font = fontFor(pixels);
  if (device && font) {
    auto previous = SelectObject(device, static_cast<HFONT>(font));
    auto wide = utf8ToWide(text);
    SIZE measured{};
    if (GetTextExtentPoint32W(device, wide.data(),
                              static_cast<int>(wide.size()), &measured))
      width = (measured.cx + 2) / rasterScale_;
    SelectObject(device, previous);
  }
  if (device)
    DeleteDC(device);
#else
  for (unsigned char c : text)
    if ((c & 0xc0) != 0x80)
      width += c < 0x80 ? size * 0.6F : static_cast<float>(size);
#endif
  if (widths_.size() >= 2048)
    widths_.clear();
  widths_.emplace(key, width);
  return width;
}
void UiTextRenderer::render(SDL_Renderer *renderer, float x, float y,
                            const char *text) {
  if (!renderer || !text || !*text)
    return;
  renderLine(renderer, x, y, localizeUiText(language_, text), 13);
}
void UiTextRenderer::renderLine(SDL_Renderer *renderer, float x, float y,
                                std::string_view localized, int size) {
  if (!renderer || localized.empty())
    return;
#if !defined(_WIN32)
  const std::string text(localized);
  SDL_RenderDebugText(renderer, x, y, text.c_str());
#else
  const int pixels =
      static_cast<int>(std::lround(std::clamp(size, 8, 64) * rasterScale_));
  auto font = fontFor(pixels);
  if (!font)
    return;
  Uint8 red{}, green{}, blue{}, alpha{};
  SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha);
  const std::string key = std::to_string(pixels) + ":" +
                          cacheKey(localized, red, green, blue, alpha);
  ++cacheUseSequence_;
  auto found = cache_.find(key);
  if (found == cache_.end()) {
    if (cache_.size() >= kMaximumCachedLabels) {
      auto oldest = std::min_element(
          cache_.begin(), cache_.end(), [](const auto &a, const auto &b) {
            return a.second.lastUsedSequence < b.second.lastUsedSequence;
          });
      cache_.erase(oldest);
    }
    auto rendered = createWindowsTextTexture(renderer, font, localized, red,
                                             green, blue, alpha);
    if (!rendered.texture.valid())
      return;
    SDL_SetTextureScaleMode(rendered.texture.get(), SDL_SCALEMODE_LINEAR);
    found = cache_
                .emplace(key, CachedText{std::move(rendered.texture),
                                         rendered.width / rasterScale_,
                                         rendered.height / rasterScale_,
                                         cacheUseSequence_})
                .first;
  } else
    found->second.lastUsedSequence = cacheUseSequence_;
  const SDL_FRect destination{x, y, found->second.width, found->second.height};
  SDL_RenderTexture(renderer, found->second.texture.get(), nullptr,
                    &destination);
#endif
}
void UiTextRenderer::renderBox(SDL_Renderer *renderer, const SDL_FRect &bounds,
                               std::string_view text, UiTextStyle style) {
  if (!renderer || bounds.w <= 0 || bounds.h <= 0)
    return;
  const auto localized = localizeUiText(language_, text);
  const float lineHeight = style.size * 1.4F;
  if (bounds.h < lineHeight)
    return;
  const int fitting = std::max(1, static_cast<int>(bounds.h / lineHeight));
  const int limit =
      style.maximumLines > 0 ? std::min(style.maximumLines, fitting) : fitting;
  const auto lines = wrapUiText(localized, bounds.w, limit, [&](auto value) {
    return measure(value, style.size);
  });
  const bool hadClip = SDL_RenderClipEnabled(renderer);
  SDL_Rect previous{};
  SDL_GetRenderClipRect(renderer, &previous);
  SDL_Rect clip{static_cast<int>(std::ceil(bounds.x)),
                static_cast<int>(std::ceil(bounds.y)),
                static_cast<int>(std::floor(bounds.w)),
                static_cast<int>(std::floor(bounds.h))};
  if (hadClip) {
    SDL_Rect intersection{};
    if (!SDL_GetRectIntersection(&clip, &previous, &intersection))
      return;
    clip = intersection;
  }
  SDL_SetRenderClipRect(renderer, &clip);
  float y = bounds.y;
  for (const auto &line : lines) {
    const float offset =
        style.align == UiTextAlign::Center  ? (bounds.w - line.width) * 0.5F
        : style.align == UiTextAlign::Right ? bounds.w - line.width
                                            : 0;
    renderLine(renderer, bounds.x + std::max(0.0F, offset), y, line.text,
               style.size);
    y += lineHeight;
  }
  SDL_SetRenderClipRect(renderer, hadClip ? &previous : nullptr);
}
