#pragma once

#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>

enum class UiLanguage { English, SimplifiedChinese };

[[nodiscard]] const char *uiLanguageCode(UiLanguage language) noexcept;
[[nodiscard]] const char *uiLanguageDisplayName(UiLanguage language) noexcept;
[[nodiscard]] UiLanguage toggledUiLanguage(UiLanguage language) noexcept;

// Player-visible strings enter the client as stable English content labels or
// formatted domain receipts. The catalog converts both exact labels and the
// reusable fragments inside formatted text; gameplay rules never depend on
// the returned display text.
[[nodiscard]] std::string localizeUiText(UiLanguage language,
                                         std::string_view englishText);

// Translate before fitting a short label. ASCII uses one display column and
// non-ASCII code points use two; clipping preserves complete UTF-8 sequences.
[[nodiscard]] std::string localizeUiLabel(UiLanguage language,
                                         std::string_view englishText,
                                         std::size_t maximumColumns);

[[nodiscard]] UiLanguage
loadUiLanguage(const std::filesystem::path &settingsPath) noexcept;
[[nodiscard]] bool saveUiLanguage(const std::filesystem::path &settingsPath,
                                  UiLanguage language) noexcept;
