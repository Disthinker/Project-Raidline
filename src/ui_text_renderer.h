#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <SDL3/SDL.h>

#include "texture.h"
#include "ui_foundation.h"
#include "ui_localization.h"

class UiTextRenderer {
public:
  UiTextRenderer() = default;
  ~UiTextRenderer();

  UiTextRenderer(const UiTextRenderer &) = delete;
  UiTextRenderer &operator=(const UiTextRenderer &) = delete;

  [[nodiscard]] bool initialize(SDL_Renderer *renderer);
  void shutdown() noexcept;
  void setLanguage(UiLanguage language) noexcept;
  [[nodiscard]] UiLanguage language() const noexcept;

  void render(SDL_Renderer *renderer, float x, float y,
              const char *englishText);

  void setRasterScale(float scale);
  void renderBox(SDL_Renderer *renderer, const SDL_FRect &bounds,
                 std::string_view englishText, UiTextStyle style = {});
  [[nodiscard]] float measure(std::string_view localizedText, int size = 18);
  [[nodiscard]] std::size_t cachedLabelCount() const noexcept { return cache_.size(); }

private:
  void *fontFor(int pixelSize);
  void renderLine(SDL_Renderer *renderer, float x, float y,
                  std::string_view localized, int size);
  float rasterScale_{1.0F};
  std::unordered_map<int, void *> fonts_;
  std::unordered_map<std::string, float> widths_;
  struct CachedText {
    Texture texture;
    float width{};
    float height{};
    std::uint64_t lastUsedSequence{};
  };

  void *platformFont_{};
  UiLanguage language_{UiLanguage::SimplifiedChinese};
  std::unordered_map<std::string, CachedText> cache_;
  std::uint64_t cacheUseSequence_{};
};
