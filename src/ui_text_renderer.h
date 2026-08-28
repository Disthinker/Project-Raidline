#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <SDL3/SDL.h>

#include "texture.h"
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

private:
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
