#include "ui_text_renderer.h"

#include <algorithm>
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
#if defined(_WIN32)
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
}

UiLanguage UiTextRenderer::language() const noexcept { return language_; }

void UiTextRenderer::render(SDL_Renderer *renderer, float x, float y,
                            const char *englishText) {
  if (renderer == nullptr || englishText == nullptr || *englishText == '\0') {
    return;
  }
  const std::string localized = localizeUiText(language_, englishText);
#if !defined(_WIN32)
  SDL_RenderDebugText(renderer, x, y, localized.c_str());
  return;
#else
  if (platformFont_ == nullptr) {
    SDL_RenderDebugText(renderer, x, y, englishText);
    return;
  }
  Uint8 red{255U};
  Uint8 green{255U};
  Uint8 blue{255U};
  Uint8 alpha{255U};
  static_cast<void>(
      SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha));
  const std::string key = cacheKey(localized, red, green, blue, alpha);
  auto found = cache_.find(key);
  if (found == cache_.end()) {
    if (cache_.size() >= kMaximumCachedLabels) {
      cache_.clear();
    }
    PlatformRenderedText rendered = createWindowsTextTexture(
        renderer, platformFont_, localized, red, green, blue, alpha);
    if (!rendered.texture.valid()) {
      return;
    }
    CachedText cached{std::move(rendered.texture), rendered.width,
                      rendered.height};
    found = cache_.emplace(key, std::move(cached)).first;
  }
  const SDL_FRect destination{x, y, found->second.width, found->second.height};
  SDL_RenderTexture(renderer, found->second.texture.get(), nullptr,
                    &destination);
#endif
}
